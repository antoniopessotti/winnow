#!/usr/bin/env ruby
#
# Copyright (c) 2008 The Kaphan Foundation
#
# Possession of a copy of this file grants no permission or license
# to use, modify, or create derivate works.
# Please contact info@peerworks.org for further information.
#

begin
  require 'spec'
rescue LoadError
  require 'rubygems'
  gem 'rspec'
  require 'spec'
end

gem 'ratom'
require 'atom'
require 'atom/pub'
require 'sqlite3'

CLASSIFIER_URL = "http://localhost:8008"
ROOT = File.expand_path(File.dirname(__FILE__))
Database = '/tmp/classifier-copy.db'

require 'active_record'
require 'active_resource'
class Tagging < ActiveRecord::Base; end
class Job < ActiveResource::Base 
  self.site = CLASSIFIER_URL + "/classifier"
end

describe "The Classifier's Item Cache" do
  before(:all) do
    Tagging.establish_connection(:adapter => 'mysql', :database => 'classifier_test', :username => 'seangeo', :password => 'seangeo')
  end
  
  before(:each) do
    system("cp -f #{File.join(ROOT, 'fixtures/valid.db')} #{Database}")
    system("chmod 644 #{Database}")
    start_classifier
    @sqlite = SQLite3::Database.open(Database)
  end
  
  after(:each) do
    @sqlite.close
    system("kill `cat /tmp/classifier-test.pid`")
  end
  
  describe "feed creation" do    
    it "should create a feed without error" do
      lambda { create_feed(:title => 'My new feed', :id => 'urn:peerworks.org:feeds#1337') }.should_not raise_error
    end
    
    it "should store feed in the database" do
      create_feed(:title => 'My new feed', :id => 'urn:peerworks.org:feeds#1337')
      @sqlite.get_first_value("select title from feeds where id = 1337").should == 'My new feed'
    end
  end
  
  describe "feed deletion" do
    it "should delete the feed without error" do
      lambda { create_feed(:title => 'My new feed', :id => 'urn:peerworks.org:feeds#1337').destroy! }.should_not raise_error
    end
    
    it "should remove the feed from the database" do
      create_feed(:title => 'My new feed', :id => 'urn:peerworks.org:feeds#1337').destroy!
      @sqlite.get_first_value("select title from feeds where id = 1337").should be_nil
    end
  end
  
  describe "entry creation" do
    it "should create an entry without error" do
      lambda { create_entry }.should_not raise_error
    end
    
    it "should create an big entry without error" do
      lambda { create_big_entry }.should_not raise_error
    end
    
    it "should store entry in the database" do
      create_entry
      @sqlite.get_first_value("select count(*) from entries where id = 1111").should == "1"
    end
  end
  
  describe "entry tokenization" do
    before(:each) do
      start_tokenizer
    end
    
    after(:each) do
      system("tokenizer_control stop")
    end
    
    it "should tokenize the item" do
      create_entry
      sleep(1)
      @sqlite.get_first_value("select count(*) from entry_tokens where entry_id = 1111").to_i.should > 0
    end
  end
  
  describe "entry deletion" do
    it "should delete the entry without error" do
      lambda { create_entry.destroy! }.should_not raise_error
    end
    
    it "should remove the entry from the database" do
      create_entry.destroy!
      @sqlite.get_first_value("select count(*) from entries where id = 1111").should == "0"
    end
    
    it "should remove the tokens from the database" do
      @sqlite.get_first_value("select count(*) from entry_tokens where entry_id = 888769").to_i.should > 0
      destroy_entry(888769)
      @sqlite.get_first_value("select count(*) from entry_tokens where entry_id = 888769").to_i.should == 0
    end
  end
  
  describe "number of items classified" do
    before(:each) do
      @item_count = @sqlite.get_first_value("select count(*) from entries;").to_i
    end
    
    it "should be equal to the number of items in the cache" do
      job = Job.create(:tag_id => 48)
      while job.progress < 100
        job.reload
      end
      
      Tagging.count(:conditions => "classifier_tagging = 1 and tag_id = 48").should == @item_count
    end
    
    describe "after item addition" do
      before(:each) do
        start_tokenizer
      end
      
      after(:each) do
        system("tokenizer_control stop")
      end
      
      it "should include the added item" do
        create_entry
        sleep(1) # let the item get into the cache
        job = Job.create(:tag_id => 48)
        while job.progress < 100
          job.reload
        end

        Tagging.count(:conditions => "classifier_tagging = 1 and tag_id = 48").should == (@item_count + 1)        
      end
      
      it "should automatically classify the new item" do
        job = Job.create(:tag_id => 48)
        while job.progress < 100
          job.reload
        end
        
        Tagging.count(:conditions => "classifier_tagging = 1 and tag_id = 48").should == @item_count
        
        create_entry
        sleep(1) # wait for it to be classified
        Tagging.count(:conditions => "classifier_tagging = 1 and tag_id = 48").should == (@item_count + 1)
      end
    end
  end
  
  def create_feed(opts)
    collection = Atom::Pub::Collection.new(:href => CLASSIFIER_URL + '/feeds')
    feed_entry = Atom::Entry.new(opts)
    collection.publish(feed_entry)
  end
  
  def create_entry
    collection = Atom::Pub::Collection.new(:href => CLASSIFIER_URL + '/feeds/426/feed_items')
    entry = Atom::Entry.new do |entry|
      entry.title = 'My Feed'
      entry.id = "urn:peerworks.org:entries#1111"
      entry.links << Atom::Link.new(:href => 'http://example.org/1111.html', :rel => 'alternate')
      entry.links << Atom::Link.new(:href => 'http://example.org/1111.atom', :rel => 'self')
      entry.updated = Time.now
      entry.content = Atom::Content::Html.new("this is the html content for entry 1111 there should be enough to tokenize")
    end
    
    collection.publish(entry)
  end
  
  def create_big_entry
    
    collection = Atom::Pub::Collection.new(:href => CLASSIFIER_URL + '/feeds/426/feed_items')
    entry = Atom::Entry.new do |entry|
      entry.title = 'My Feed'
      entry.id = "urn:peerworks.org:entries#1111"
      entry.links << Atom::Link.new(:href => 'http://example.org/1111.html', :rel => 'alternate')
      entry.links << Atom::Link.new(:href => 'http://example.org/1111.atom', :rel => 'self')
      entry.updated = Time.now
      entry.content = Atom::Content::Html.new("&lt;p&gt;&lt;img src='http://www.gravatar.com/avatar.php?gravatar_id=e9d797bffffd51cf67866a6e5af8648c&amp;amp;size=80&amp;amp;default=http://use.perl.org/images/pix.gif' alt='' style='float: left; margin-right: 10px; border: none;'/&gt;I&amp;#8217;ve often wondered why Judaism doesn&amp;#8217;t tick me off like Christianity and Islam do. Is it because Jews don&amp;#8217;t try to convert anyone? Perhaps. Is it because of the Holocaust? Maybe. Is it because of my own Jewish roots and my Jewish-atheist grandfather? I doubt it. After all, I also have Christian roots and had a devout Catholic grandmother.&lt;/p&gt;
    &lt;p&gt;I think it might be because Judaism is not about belief. You wouldn&amp;#8217;t hear this, for example, in a church or a mosque:&lt;/p&gt;
    &lt;p&gt;&lt;a href='http://www.glumbert.com/media/barmitzvah'&gt;&lt;img src='http://farm3.static.flickr.com/2025/2339772983_0d72afd3d9_m.jpg' alt='Barmitzvah Movie'/&gt;&lt;/a&gt;&lt;/p&gt;
    &lt;p&gt;Even in Israel, where Jews are often accused of being fundamentalists, and where orthodoxy arguably does have a stronghold (the differences between orthodoxy and fundamentalism in all religions requiring a completely separate discussion&amp;#8230;) &lt;a href='http://www.jewcy.com/post/secular_israelis_seek_jewish_tradition_belief_god_not_required'&gt;belief in God is optional&lt;/a&gt;. You can, with a straight face, call someone a secular Jew or even an atheist Jew, and they won&amp;#8217;t even be insulted. Who among you has ever heard of a secular Christian or an atheist Muslim?&lt;/p&gt;
    &lt;blockquote&gt;&lt;p&gt;The ambivalence about Judaism in Israel became clear to me one night as I sat drinking in an alleyway bar in Tel Aviv with my Israeli friend Omer. Omer has been studying abroad in Germany for the past few years, and admitted that he felt disconnected there, and had started attending a Friday night dinner with other Jewish students. &#x201C;My father would disown me if he knew I was lighting Shabbat candles,&#x201D; said Omer guiltily. &#x201C;We come from a long line of staunch Tel Aviv atheists.&#x201D;&lt;/p&gt;
    &lt;p&gt;In order to counteract this deep rooted aversion to religion, the Jewish Renewal movement (different from the 1960s American movement of the same name) takes a more flexible approach, focusing on ritual, tradition and spirituality rather than outright faith. While the term &#x201C;secular synagogue&#x201D; may seem like an oxymoron,to proponents of Jewish Renewal, it&#x2019;s the basis of their ideology.&lt;/p&gt;&lt;/blockquote&gt;
    &lt;p&gt;When reading Shalom Auslander&amp;#8217;s &lt;em&gt;Foreskin&amp;#8217;s Lament&lt;/em&gt;, the author&amp;#8217;s story about growing up in an abusive home and a suffocating Orthodox community, I just didn&amp;#8217;t find the bile rising to my throat the way I do when I read &lt;em&gt;Infidel&lt;/em&gt; or any of the plethora of Christian de-conversion memoirs I&amp;#8217;ve read over the past couple of years.&lt;/p&gt;
    &lt;p&gt;I can&amp;#8217;t believe I haven&amp;#8217;t made &lt;a href='http://www.shalomauslander.com/book_foreskins_lament.php'&gt;&lt;em&gt;Foreskin&amp;#8217;s Lament&lt;/em&gt;&lt;/a&gt; one of our reading selections! I guess it&amp;#8217;s because I&amp;#8217;m trying to cover a range of topics and not inundate you solely with my own preoccupations. But this book is definitely in the must read category. Jewish writers, for some reason, can make these things &lt;em&gt;funny&lt;/em&gt;, while Christians and Muslims seem to think that humor is a sin.&lt;/p&gt;
    &lt;blockquote&gt;&lt;p&gt;&amp;#8220;If you read this while you&amp;#8217;re eating, the food will come out your nose. Foreskin&amp;#8217;s Lament is a filthy and slightly troubling dialogue with God, the big, old, physically abusive ultra orthodox God who brought His Chosen People out of Egypt to torture them with non-kosher Slim Jims. I loved this book and will never again look at the isolated religious nutjobs on the fringe of American society with anything less than love and understanding.&amp;#8221;&lt;/p&gt;&lt;/blockquote&gt;
    &lt;p&gt;On the other hand, &lt;a href='http://www.nydailynews.com/entertainment/movies/2008/03/15/2008-03-15_hasidic_actor_walks_off_portman_movie.html'&gt;Jewish fundamentalists are stupid control freaks, just like fundies of other religions&lt;/a&gt;:&lt;/p&gt;
    &lt;blockquote&gt;&lt;p&gt;Abe Karpen, 25, a married father of three, was cast as [Natalie] Portman&amp;#8217;s husband in &amp;#8220;New York I Love You,&amp;#8221; a film composed of 12 short stories about love in the five boroughs.&lt;/p&gt;
    &lt;p&gt;&amp;#8220;I am backing out of the movie,&amp;#8221; said Karpen, a kitchen cabinet salesman. &amp;#8220;It&amp;#8217;s not acceptable in my community. It&amp;#8217;s a lot of pressure I am getting. They [the rabbis] didn&amp;#8217;t like the idea of a Hasidic guy playing in Hollywood.&lt;/p&gt;
    &lt;p&gt;&amp;#8220;I have my kids in religious schools and the rabbi called me over yesterday and said in order for me to keep my kids in the school I have to do what they tell me and back out,&amp;#8221; Karpen said.&lt;/p&gt;&lt;/blockquote&gt;
    &lt;p&gt;Well, those are a few of my subjective and highly personal thoughts on the matter. Discuss.&lt;/p&gt;")
    end
    
    collection.publish(entry)
  end
  
  def destroy_entry(id)
    Atom::Entry.new do |e|
      e.links << Atom::Link.new(:href => CLASSIFIER_URL + "/feed_items/#{id}", :rel => 'edit')
    end.destroy!
  end
  
  def start_classifier   
    classifier = File.join(ROOT, "../src/classifier")
    
    if ENV['srcdir']
      classifier = File.join(ENV['PWD'], '../src/classifier')
    end
    classifier_cmd = "#{classifier} -d --pid /tmp/classifier-test.pid " +
                                                   "-t http://localhost:8010/tokenize " +
                                                   "-l /tmp/classifier-item_cache_spec.log " +
                                                   "-c #{File.join(ROOT, "fixtures/real-db.conf")} " +
                                                   "--db #{Database} 2> /dev/null" 
    system(classifier_cmd)
    sleep(0.0001)
  end
  
  def start_tokenizer
    system("tokenizer_control start -- -p8010 #{Database}")
    sleep(1)
  end
end
